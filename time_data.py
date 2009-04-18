#!/usr/bin/python

import time
import commands
import sys

EXPRESSIONS = [
        """commands.getoutput("date")""",
        """commands.getoutput("TZ=UTC date")""",
        """time.localtime()""",
        """time.gmtime()""",
        """time.timezone""",
        ]

def describe_localtime():
    localtime_desc_dict = {
            (-7200, 0):   ("Proper IST", True), 
            (-10800, 0):  ("Fake DST", False),
            (-7200, 1):   ("Proper DST", True),
            (-10800, 1):  ("Double DST", False),
            }

    desc, value = localtime_desc_dict.get((time.timezone, time.localtime()[8]),
                                          ("Unknown", False))
    if not value: print "ERROR:",

    print "Time looks like %s" % desc

    return value

for expr in EXPRESSIONS:
    print "% 40s = %s" % (expr, eval(expr))

if describe_localtime(): sys.exit(0)
else: sys.exit(1)

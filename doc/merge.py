#!/usr/bin/env python

import os
import sys
import getopt
import json

outfile = "merged.json"

try:
    opts, args = getopt.getopt(sys.argv[1:], "o:")

except getopt.GetoptError as err:
    print str(err) # will print something like "option -a not recognized"
    sys.exit(2)
for o, a in opts:
    if o == "-o":
        outfile = a

merged = dict()
for f in args:
    if not os.path.exists(f):
        print "WARN: %s does not exist", f

    fp = open(f, "r")
    d = json.load(fp)
    fp.close()

    for key in d:
        print key,
        if merged.has_key(key):
            print "WARNING: merged already has key", key
            merged[key].update(d[key])
        else:
            merged[key] = d[key]

    print merged
    print

fp = open(outfile, "w")
json.dump(merged, fp, indent=4, sort_keys=True)
fp.close()

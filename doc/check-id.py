#!/usr/bin/env python

import os
import sys
import getopt

outfile = "unikid.json"
selfupdate = 0
verbose = 0

try:
    opts, args = getopt.getopt(sys.argv[1:], "o:av")
except getopt.GetoptError as err:
    print str(err) # will print something like "option -a not recognized"
    sys.exit(2)

for o, a in opts:
    if o == "-o":
        outfile = a
    if o == "-a":
        selfupate = 1
    if o == "-v":
        verbose = 1

for f in args:
    if not os.path.exists(f):
        print "WARN: %s does not exist", f

    try:
        fp = open(f, "r")
    except IOError:
        print "WARN: Unable to open %s", f
        sys.exit(2)

    if selfupdate:
        outfile = f
    try:
        fo = open(outfile, "w+")
    except IOError:
        print "WARN: Unable to open %s", f
        sys.exit(2)

    lines = fp.readlines()
    fp.close()

    curid = 1
    refcount = 0
    idlist = {}
    myid = []
    for myline in lines:

        if "{" in myline:
            refcount +=1
            myid.append(curid)
            curid = 1
            #print "-->Entering level ", refcount
            idlist[refcount] = {}

        if "}" in myline:
            #print "<--Leaving level ", refcount
            del idlist[refcount]
            curid = myid.pop()
            refcount -=1

        try:
            key_id, value = myline.split(":", 1)
        except ValueError:
            #print "Nothing to do"
            fo.write(myline)       
            continue

        key_id = key_id.strip('\"\t\n\r ')

        value = value.strip(',\"\t\n\r ')

        if key_id in idlist[refcount]:
            newkey_id = key_id + str(curid)
            while newkey_id in idlist[refcount]:
                curid +=1
                newkey_id = key_id + str(curid)

            if verbose:
                print "key ", key_id, " changed into ", newkey_id

            myline = myline.replace(key_id, newkey_id, 1)
            key_id = newkey_id

        #print "Add <", key_id, "> has value <", value, ">"
        idlist[refcount][key_id] = value

        fo.write(myline)

fp.close()
fo.close()









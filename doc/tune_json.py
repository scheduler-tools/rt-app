#!/usr/bin/env python

import argparse
import collections
import json
import os
import re
import shutil
import sys
import tempfile


def find_dict_by_key(doc, key):
    if key in doc and type(doc[key]) is collections.OrderedDict:
        return doc[key]

    for k in doc:
        if type(doc[k]) is collections.OrderedDict:
            return find_dict_by_key(doc[k], key)


def dict_find_and_replace_value(dic, key, val):
    for k in dic:
        if type(dic[k]) is collections.OrderedDict:
            dict_find_and_replace_value(dic[k], key, val)
        if k == key:
            dic[k] = val


def dict_of_loading(dic):
    if not 'run' in dic:
        return False, None

    for k in dic:
        if 'timer' in k and 'period' in dic[k]:
            return True, k
    else:
        return False, None


def calculate_and_update_loading(dic, loading):
    of_loading, timer_id = dict_of_loading(dic)

    if of_loading:
        period = dic[timer_id]['period']
        run = period * loading / 100
        dic['run'] = run

    for k in dic:
        if type(dic[k]) is collections.OrderedDict:
            calculate_and_update_loading(dic[k], loading)


# strip comments in json file and load the file as a dict
def load_json_file(filename):
    try:
        f = open(filename, 'r')
    except:
        print 'ERROR: Unable to open %s' %filename
        sys.exit(2)

    comment_re = re.compile(
        '(^)?[^\S\n]*/(?:\*(.*?)\*/[^\S\n]*|/[^\n]*)($)?',
        re.DOTALL | re.MULTILINE)

    content = ''.join(f.readlines())
    f.close()

    match = comment_re.search(content)
    while match:
        content = content[:match.start()] + content[match.end():]
        match = comment_re.search(content)

    return json.JSONDecoder(object_pairs_hook=collections.OrderedDict).decode(content)


def dump_json_file(doc, outfile):
    tmp = tempfile.NamedTemporaryFile(delete=False)
    json.dump(doc, tmp, indent=4, sort_keys=False)
    tmp.close()

    shutil.move(tmp.name, outfile)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()

    parser.add_argument('-f', '--file', dest='infile', default='', help='input json filename')
    parser.add_argument('-o', '--out', dest='outfile', default='workload.json', help='output json filename');
    parser.add_argument('--instance', default=0, type=int, help='number of thread instance')
    parser.add_argument('--period', default=0, type=int, help='period of each thread/phase (us)')
    parser.add_argument('--run', default=0, type=int, help='run time of each thread/phase (us)')
    parser.add_argument('--sleep', default=0, type=int, help='sleep time of each thread/phase (us)')
    parser.add_argument('--loop', default=0,type=int, help='loop count of each thread/phase (-1 as infinite loop)')
    parser.add_argument('--loading', default=0, type=int, help='loading of each thread (%%)')
    parser.add_argument('--key', type=str, help='the key id of thread/phase in which the parameters will be changed')
    parser.add_argument('--duration', default=0, type=int, help='max duration of the use case (s)')


    args = parser.parse_args()

    if not os.path.isfile(args.infile):
        print 'ERROR: input file %s does not exist\n' %args.infile
        parser.print_help()
        sys.exit(2)

    doc = target = load_json_file(args.infile)

    if args.key:
        target = find_dict_by_key(doc, args.key)
        if not target:
            print 'ERROR: key id %s is not found' %args.key
            sys.exit(2)

    if args.instance > 0:
        dict_find_and_replace_value(target, 'instance', args.instance)

    if args.period > 0:
        dict_find_and_replace_value(target, 'period', args.period)

    if args.duration > 0:
        dict_find_and_replace_value(target, 'duration', args.duration)

    if args.run > 0:
        dict_find_and_replace_value(target, 'run', args.run)

    if args.sleep > 0:
        dict_find_and_replace_value(target, 'sleep', args.sleep)

    if args.loop > 0 or args.loop == -1:
        dict_find_and_replace_value(target, 'loop', args.loop)

    if args.loading > 0:
        calculate_and_update_loading(target, args.loading);

    dump_json_file(doc, args.outfile)
